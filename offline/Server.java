package offline;

import java.io.IOException;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class Server{
    Map<String,Socket> connectedclients;
    Map<String,Socket> disconnectedclients;
    //List<ClientHandler> onlineClients;
    public List<String> users;//any logged in user
    Map<String,List<String>> UploadedPublicFilesByUsers;
    Map<String,List<String>> UploadedPrivateFilesByUsers;
    int currentServerSize=0;
    Map<String,ClientHandler> allRequests;
    Map<String,List<String>> inbox;
    public Server(){
        connectedclients=new HashMap<>();
        disconnectedclients=new HashMap<>();
        users=new ArrayList<>();
        //onlineClients=new ArrayList<>();
        UploadedPublicFilesByUsers=new HashMap<>();
        UploadedPrivateFilesByUsers=new HashMap<>();
        allRequests=new HashMap<>();
        inbox=new HashMap<>();
    }
    public void start() {
        try (ServerSocket serverSocket = new ServerSocket(12345)) {
            System.out.println(("Server started"));
            while (true){
                Socket clientSocket = serverSocket.accept();
                ClientHandler clientHandler = new ClientHandler(clientSocket,this);
                clientHandler.start();
            }
        }catch(IOException e){
            e.printStackTrace();
        }

    }
    public boolean isNameTaken(String username,Socket clientSocket){
        if(connectedclients.containsKey(username) && connectedclients.get(username)!=clientSocket){
            return true;
        }
        return false;
    }
    public boolean isInDisconnectedList(String username){
        if(disconnectedclients.containsKey(username)){
            return true;
        }
        return false;
    }
    public String getConnectedUsers(){
        StringBuilder sb=new StringBuilder();
        for(String user:connectedclients.keySet()){
            sb.append(user).append(",");
        }
        return sb.toString();
    }
    public String getDisconnectedUsers(){
        StringBuilder sb=new StringBuilder();
        for(String user:disconnectedclients.keySet()){
            sb.append(user).append(",");
        }
        return sb.toString();
    }
    public static void main(String[] args) throws IOException, ClassNotFoundException{
        Server server=new Server();
        server.start();
    }
}